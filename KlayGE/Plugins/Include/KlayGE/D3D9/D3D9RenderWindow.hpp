#ifndef _D3D9RENDERWINDOW_HPP
#define _D3D9RENDERWINDOW_HPP

#include <d3d9.h>
#include <KlayGE/COMPtr.hpp>
#include <KlayGE/RenderWindow.hpp>
#include <KlayGE/D3D9/D3D9Adapter.hpp>

#pragma comment(lib, "KlayGE_RenderEngine_D3D9.lib")

namespace KlayGE
{
	struct D3D9RenderSettings;

	class D3D9RenderWindow : public RenderWindow
	{
	public:
		D3D9RenderWindow(const COMPtr<IDirect3D9>& d3d, const D3D9Adapter& adapter,
			const std::string& name, const D3D9RenderSettings& settings);
		~D3D9RenderWindow();

		LRESULT MsgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

		void Destroy();

		bool Active() const;

		bool Closed() const;

		bool Ready() const;
		void Ready(bool ready);

		void Reposition(int left, int top);
		void Resize(int width, int height);
		void SwapBuffers();

		HWND WindowHandle() const;

		const std::wstring& Description() const;

		const D3D9Adapter& Adapter() const;
		const COMPtr<IDirect3DDevice9>& D3DDevice() const;

		void CustomAttribute(const std::string& name, void* pData);

		bool RequiresTextureFlipping() const;

		// Method for dealing with resize / move & 3d library
		void WindowMovedOrResized();

	protected:
		std::string	name_;

		HWND	hWnd_;				// Win32 Window handle
		bool	active_;			// Is active i.e. visible
		bool	ready_;				// Is ready i.e. available for update
		bool	closed_;

		D3DMULTISAMPLE_TYPE multiSample_;

		static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg,
			WPARAM wParam, LPARAM lParam );

		// -------------------------------------------------------
		// DirectX-specific
		// -------------------------------------------------------

		D3D9Adapter					adapter_;

		// Pointer to the 3D device specific for this window
		COMPtr<IDirect3D9>			d3d_;
		COMPtr<IDirect3DDevice9>	d3dDevice_;
		D3DPRESENT_PARAMETERS		d3dpp_;
		
		COMPtr<IDirect3DSurface9>	renderSurface_;
		COMPtr<IDirect3DSurface9>	renderZBuffer_;

		std::wstring		description_;
	};
}

#endif			// _D3D9RENDERWINDOW_HPP
